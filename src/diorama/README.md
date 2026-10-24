# Diorama 3D overworld renderer

Built only with `make -f Makefile_pc DIORAMA=1` (OpenGL 3.3 core through SDL2). The
GBA build and the default native build do not compile it. Every file is wrapped in
`#ifdef ENABLE_DIORAMA`.

## Provenance

Imported from **ZallaxDev/pokeemerald-voxel-project** at commit
`cfb429bd145e81dbeab822f30b91a4e4f009a7d2` (2026-08-13), unchanged unless listed
below:

| Path here | Notes |
| --- | --- |
| `src/diorama/**`, `include/diorama/**` (except `*.generated.h`) | Renderer: scene snapshot and exchange, metatile atlas, terrain mesh, object/terrain/compositor GL passes, rules runtime. |
| `tools/diorama_rules/*.py`, `tools/diorama_tree/*.py`, `diorama_rules.mk` | Offline compilers for the rule tables and the voxel tree model. **Adapted here** for pokeemerald-expansion (see below). |
| `data/diorama/**` | Rule sources (JSON) read by the compiler. |
| `tree_model/emerald_tree.vox` | Voxel tree model compiled into `src/data/diorama/tree_model.generated.c`. |
| `tests/diorama/*.c` | The project's unit tests for the renderer modules. |

Licence: MIT for the multiplatform-port modifications ("Copyright (c) 2026
pokeemerald-multiplatform contributors"; full text in that repository's `LICENSE`).
It does not cover the pret decompilation or any Pokémon game material. Keep this
notice with any distribution of these files.

### Modelled buildings (ZallaxDev/pokeemerald-3Ds-dualscreen)

| Path here | Notes |
| --- | --- |
| `src/diorama/scene/buildings.c`, `include/diorama/buildings.h` | Port of the 3DS project's `3ds_port/src/voxel/voxel_building.c` (commit `4a699e3`): the VXB7 building table reader, read from memory instead of RomFS, and its front-face query (`CollectFronts`, `FrontCellAt`, `VoxelBuildings_FrontAt` as `DioramaBuildings_FrontAt`). |
| `src/diorama/gl/gl_building_renderer.c`, `include/diorama/gl_building_renderer.h` | New: draws the placed models with OpenGL, coloured from the live BG palette (the placement logic follows the 3DS project's `VoxelBuildings_EmitSome`). |
| `src/diorama/gl/gl_object_renderer.c` (`ModelPush`) | Port of the 3DS project's `ModelPush` (`3ds_port/src/voxel/voxel_entities.c`): a walker's card comes a pixel in front of the furniture it stands before. The upright-card depth (`UprightDepth` in the sprite shader) is new. |
| `src/field_door.c` (`DioramaDoor_Get`), `gl_object_renderer.c` (`FindDoor`, `DrawDoorOpening`, `DoorDusk`) | Port of the 3DS project's open door (`PortDoor_Get` in its `src/field_door.c`; `EmitDoor`, `DoorLight` and `VoxelBuildings_DoorWall` in `3ds_port/src/voxel/`): a black opening on a modelled building's wall widens as the door opens, and a walker going through darkens and is gone. Changes: swinging doors open too, not only sliding ones; the expansion's door sizes (1x1, 1x2, 2x2 left and right); no opening on an unmodelled building. |
| `src/diorama/gl/gl_object_renderer.c` (`DrawCastShadow`) | Port of the 3DS project's `EmitCastShadow` (`voxel_entities.c`): in the sun a walker's shadow is its own picture laid on the ground along the sun, in the 3DS's dark blue at alpha 0.36 scaled by the sun's strength and by the light on the walker. Changes: flat ground (no relief lift per corner); with no sun (indoors, bad weather) the Diorama's round shadow is kept. |
| `src/diorama/gl/gl_grade.c`, `include/diorama/gl_grade.h` | The 3DS project's colour grade (`3ds_port/src/voxel/voxel_grade.c`): saturation 1.30 about the luma, in the 3D view only. The 3DS grades its textures; here the terrain, building and sprite shaders grade each texel before lighting. `PKMN_DIORAMA_GRADE=0` turns it off. |
| `src/diorama/gl/gl_light.c`, `include/diorama/gl_light.h` | Port of the 3DS project's outdoor light (`3ds_port/src/voxel/ctr_voxel.c`: `LightFor`, `FogLight`, `SetGrade`; `voxel.v.pica`; `VoxelWorld_Weather` in `voxel_world.c`; commit `4a699e3`): the tint warm in the sun and cool in shade, picked by a point's brightness (fully the shade's at 0.62 or below), and the distance haze from 1.05x the eye-to-player distance, both as the weather has them (sun, rain, fog, ash and sand, shade; a cave's cold fog). Indoors (a building or a secret base) none of it. The sun dapples (`MakeDapple` with `DappleLattice`, `Quintic`, `DappleNoise`; `DappleUniforms`; `VoxelWorld_Weather`): the same 128x128 noise, laid along the sun, drifting 0.25 tiles a second, anchored to the world across map crossings. Changes: per fragment in the terrain, building and sprite shaders instead of per vertex (a sprite takes the dapple at its feet); the haze colour follows the palette's night tint and fades (the `TreeTint` line); at night (the game's clock; the 3DS has none) dapples at half strength and no rays or dust. `PKMN_DIORAMA_LIGHT=0` turns it off. |
| `src/diorama/gl/gl_air.c`, `include/diorama/gl_air.h` | Port of the 3DS project's light in the air: sunlit dust (`ctr_voxel.c` `DrawMotes`, `MoteRandom`), sun rays (`DrawRays`, `MakeRays`, `RayStrength`) and bloom (`3ds_video.c` `VoxelBloomPrepare`, `VoxelBloomCompose`), at the strengths `LightFor` gives each weather. Changes: the dust fills the scene's own 33x33 cells (83 specks, the 3DS's density) and fades at the box's edge, since this camera can see past the cells into the dark where dust read as stars; the rays are computed per pixel (the 3DS interpolates an 8x5 mesh) and only drawn over the scene, not that dark; the bloom's copy is a `glBlitFramebuffer`. Also the game's fog in the scene (`MakeFog`, `DrawFogBanks`: two drifting banks of mist from four octaves of the same noise, knee and chest high, depth tested, hazed like the ground) in place of the flat fog overlay, and in a cave's fog the dark ring round the player (`MakeGloom`, `PlaceGloom`); the banks also fade out over the last 3 tiles before the scene's cells end. `PKMN_DIORAMA_AIR=0` turns all of it off (the flat fog comes back). |
| `src/diorama/scene/tree_sprites.c`, `include/diorama/tree_sprites.h`, `src/diorama/gl/gl_tree_sprites.c`, `include/diorama/gl_tree_sprites.h`, `tools/diorama_trees_3ds/**` | Port of the 3DS project's trees (`3ds_port/src/voxel/voxel_tree.c`: `VoxelTree_Part`, `VoxelTree_GroundMetatile`, `EmitCell`, `EmitSmallCell`; `VoxelWorld_UsesTreeSprites`; `scripts/gen_voxel_trees.py`; its own tree art, `assets/voxel/trees/`), an option next to the voxel project's trees (F8 in the game, kept in `pokeemerald.cfg`; `PKMN_DIORAMA_TREES=3ds` for one run): the trunk's art on the ground and a cut-out crown at 50 degrees. Changes: the tree cells are published as ground in the snapshot (`ApplyTreeSprites`) so the rules and terrain treat them so; the crowns cast into the shadow map and follow the trees' palette line (fades, night) like the voxel trees; crowns are lit flat (the 3DS lights them as rounded); the crowns brightened 1.12x and all texels kept to 5 bits a channel at pack time, as the 3DS loads them (`BrightenCrowns`). See `tools/diorama_trees_3ds/README.md`. |
| `src/diorama/gl/gl_field_light.c`, `include/diorama/gl_field_light.h`, `scene_snapshot.c` (`ReadFieldLight`, `CopyFieldLight`) | Port of the 3DS project's dark of a cave that needs Flash and of the Battle Pyramid in its voxel view (`3ds_port/src/3ds_game_full.c` `CaptureFieldLight`; `3ds_video.c` `CtrVideo_SetFieldLight`, `MakeFlashLight`, `VoxelFlashLight`, `FadeColor`, `FadeChannelTarget`, `DetectPaletteFade`; `ctr_voxel.c` `CtrVoxel_PlayerLightSpot`; commit `4a699e3`): the game's window circle read back from the scanline buffer, eased as it opens, drawn as a soft ellipse on the floor round the player with an amber rim, a warm pool, a flicker and Flash's glow, the dark following the palette's fades. Before it a dark cave was drawn fully lit in 3D. Changes: one shader pass instead of two textures and four rectangles; the rim is tinted towards the fade's colour itself, so inside and outside the quad match during a fade. |
| `src/diorama/gl/gl_shadow.c`, `include/diorama/gl_shadow.h` | New: the sun's depth map. The sun's direction and the 0.70 ambient are the 3DS project's (`voxel_lighting.h`); the 3DS bakes its shadows on the CPU, this draws them on the GPU. |
| `tools/diorama_buildings/**` | The 3DS project's building generator; see its README for the files, licence and changes. |

### 3D battles (ZallaxDev/pokeemerald-3Ds-dualscreen, graphics only)

The battle scene of the 3DS project (commit `4a699e3`) on one PC screen: the
world the field last drew, seen from a battle camera, behind the battle's own
GBA picture. The battle engine, controllers, sprites, healthboxes, menus, move
animations and their timing are unchanged; nothing here writes game memory.

| Path here | Notes |
| --- | --- |
| `src/diorama/scene/battle_stage.c`, `include/diorama/battle_stage.h` | Port of `3ds_port/src/voxel/voxel_battle.c`'s stage search (where the battle stands near the player: fitting ground, distance), written in GBA picture terms instead of the 3DS screen's and run at once instead of a slice per frame. New (second round): its view terms replaced by what the battle camera sees - the share of the picture showing nothing drawn (rays through the cells), anything in front of each battler's own picture (columns projected corner by corner, leaning crowns), walls pressed behind a battler - weights tuned on 44 maps (`DioramaStage_Measure`). |
| `src/diorama/scene/battle_capture.c`, `include/diorama/battle_scene.h` | Port of `voxel_battle.c`'s ground kinds and battler shadows and of `voxel_world.c`'s scenery fade fit (`VoxelWorld_ScreenFade`); new: the per-frame capture, the battle-screen check. |
| `src/diorama/gl/gl_battle.c`, `include/diorama/gl_battle.h` | Port of the battle camera and its glide (`3ds_port/src/voxel/ctr_voxel.c`: crane, dolly zoom, landing push and quake) and of `RenderBattleWorld`'s intro effects and contact shadows (`3ds_port/src/3ds_video.c`: speed lines, vignette, cinema bars, landing flash). New: the single-screen layout (picture on the bottom edge, its background carried out to wide windows), the world kept while the camera stands still, the classic-view switch (F4) during a battle, the field's 3D fade back in; second round: water and flowers moving where a world redraw is cheap (timed in each glide), the battle's own scenery where the world ends (far plane, and curtains on indoor maps' edges with the terrain shader's camera), the stage survey and cell dump (test tools); third round: the tile animations paced by measured frame times (every battle frame timed start to start; the glide's frames choose the first step, every later redraw frame moves it), the world drawn at the display settings' render size and antialiasing (`gl_target.h`), the picture at the field's whole-number scale, a move's own background carried past the picture (`gDioramaSceneWide`). |
| `src/platform/gba_easy_draw.c` (`ENABLE_DIORAMA` parts) | New: the scenery layers drawn see-through with the GBA's own blending carried in a per-pixel cover (premultiplied alpha), and each line's edge colour. The 3DS project does the same on its GPU (`sWorldLayers`). Second round: BG3's own picture in its unfaded colours kept aside (`gDioramaSceneArt`) for where the world ends. Third round: how the screen would go on past the picture while BG3 holds a move's background (`gDioramaSceneWide`: the part of the tilemap the move filled repeated, or mirrored at the picture's edges where a repeat would show a seam, judged from its unfaded colours; its scroll, palette, brightness, blending and window state as on screen), and which layer each edge colour is. |
| `src/battle_bg.c`, `src/battle_anim.c`, `src/battle_intro.c` (`ENABLE_DIORAMA` parts) | New hooks: whether BG3 holds the scenery or a move's background (`LoadMoveBg`), and whether the intro is sliding (the 3DS port's `CtrBattleBg_MoveBgShown`, `CtrBattleIntro_Sliding`); third round: how much of BG3's tilemap the move background filled (`DioramaBattle_NoteMoveBg`). |
| `src/diorama/gl/gl_terrain_renderer.c`, `gl_building_renderer.c`, `gl_tree_sprites.c`, `gl_light.c` (`DioramaGLLight_GetHaze`), `src/diorama/terrain/terrain_mesh.c` | The eye's height, distance and screen offset as uniforms (`DioramaCameraRig`); with the rig inactive the overworld's values, so the field is drawn exactly as before. |
| `gl_battle.c` (`DioramaGLBattle_ComposeFieldBands`, `_ComposeFieldPicture`), `gl_compositor.c` (their place in `DioramaGL_Present`), `battle_capture.c` (map sprites, the field's mosaic), `src/platform/gba_easy_draw.c` (line shifts), `src/sprite.c` and `src/battle_transition.c` (`ENABLE_DIORAMA` parts) | Port of the 3DS project's battle transition over its voxel field (`3ds_port/src/3ds_video.c` `RenderTransition`): the field's map layers see-through and its map sprites (placed on the map, or the weather's) left out of the GBA picture, the 3D field copied out and laid back in bands of lines moved as the transition moves the field's layers, the transition's own picture over it. New: the hooks that mark map sprites as the OAM buffer is built and say whether a transition plays; second round: the people, the weather and a dark cave's light drawn unmoved between the bands and the picture (the 3DS project moves them with the bands), the Blur transition's mosaic on the 3D field, the last field held if a frame's is refused. `PKMN_DIORAMA_TRANSITION_2D=1` keeps the cut to the flat picture. |
| `src/tileset_anims.c`, `gl_compositor.c` (`DioramaGL_SetBattleTiles`) (`ENABLE_DIORAMA` parts) | New (second round): the field's tile animations run into the battle world's copy of the BG tiles (`DioramaBattle_AnimateFieldTiles`: no VRAM, no queue, no palette effects), the atlas slots updated where tiles changed. The 3DS project's battle world stands still. |
| `gl_battle.c` (`StageCellAt`) | The stage search's cells from the Diorama's scene: the modelled buildings, the voxel trees as the model stands (about two tiles tall; a one-row tree's crown on the row above its trunk) and the 3DS project's trees (`gl_tree_sprites.c`'s crown heights), anything the rules raise. |

Licence of the 3DS project's port code: MIT, "Copyright (c) 2026 ZallaxDev and
Pokémon Emerald 3Ds Dual Screen contributors" (its `LICENSE-PORT.md`; it does not
cover Pokémon Emerald or the pret decompilation). Its voxel code credits the MIT
pokeemerald-multiplatform port for the logic it reuses (its `3ds_port/src/voxel/
NOTICE.md`), the notice above. Keep both notices with these files.

## Generated files (not in Git)

`src/data/diorama/diorama_rules.generated.c`, `include/diorama/rules.generated.h`,
`src/data/diorama/tree_model.generated.c`, `include/diorama/tree_model.generated.h`
and `src/data/diorama/buildings.generated.c` (via `build/diorama/buildings.bin`)
are produced from this tree's maps and tilesets by `diorama_rules.mk` during a
`DIORAMA=1` build. For this fork they come out byte-identical to the voxel
project's tables (rules SHA-256 `9ef5c6e8...`), because the Emerald maps and
tilesets are unchanged.

## Changes for pokeemerald-expansion

* `tools/diorama_rules/catalog.py`: graphics symbols declared with `INCGFX_*`
  (and `ALIGNED(n)`) as well as `INCBIN_*`; FireRed/LeafGreen tilesets, layouts
  (`layout_version` "frlg") and map groups (`*_Frlg`) are skipped, so the catalog
  is Emerald's 75 tilesets, 441 layouts and 518 maps with unchanged ids.
* `tools/diorama_rules/emerald_compositor.py`: JASC `.pal` palettes (converted the
  way gbagfx does; verified identical for all 2112 tileset palettes); 4bpp data
  generated under `build/assets`; only animation callbacks of the Emerald tilesets.
* Game hooks (all `#ifdef ENABLE_DIORAMA`, GBA line numbers kept): `src/main.c`
  (frame begin/end), `src/overworld.c` (publish the overworld snapshot after
  visible-spawn updates), `src/fieldmap.c` (map changed), `src/field_door.c`
  (the open door).

## Changes to the voxel project's behaviour

* Scripted scenes (controls locked: a door warp, a cutscene, the nurse healing)
  stay in 3D, as in the 3DS project; the voxel project drew them in 2D. A frame
  the 3D view cannot draw still falls back (`snapshot_exchange.c`,
  `presentation_transition.c`). The camera interpolates in them as in free play.
* Screen fades on the field (a warp's fade out and in) stay in 3D: the scene is
  coloured from the faded palettes. `PKMN_DIORAMA_FADE_2D=1` restores the cut to
  the 2D view for the fade (`scene_snapshot.c`).
* The 3D view is drawn while the player is hidden (gone through a door); the voxel
  project required a visible player (`gl_object_renderer.c`).
* The voxel tree model's baked colours follow the palette's fade and night tint:
  a per-channel line fitted from the trees' BG palette each frame
  (`gl_terrain_renderer.c`, `TreeTint`); by day it is the identity.

## Resolution, quality and the PC's own interface (project-owned, 2026-10-10)

* `gl/gl_target.c`: the scene's render target. Drawn straight into the window when
  its size is the window's and there is no multisampling (windowed mode, or a
  fullscreen mode at the chosen resolution: the path before these settings);
  otherwise into an offscreen framebuffer at the chosen resolution's height and
  the window's shape (multisampled if the quality asks), then scaled into the
  window. The sun's depth map and the bloom return to that framebuffer
  (`DioramaGLTarget_Framebuffer`, `DioramaGLTarget_ReadableFramebuffer`).
* `gl/gl_compositor.c`: the 240x160 picture (text boxes, menus, 2D scenes) in
  whole-number steps at the window's own resolution (the same size as before at
  16:9, sharp at any other); the weather layer at one whole-number scale covering
  the window instead of stretched to its shape; `DioramaGL_SetOverlay` for the
  PC's interface, drawn last.
* `gl/gl_quality.c`: the player's quality: the sun depth map's side (off, 1024,
  2048 as before, 4096; `gl_shadow.c` makes the map again when it changes), the
  weather light, dapples and colour grade, the light in the air. The
  `PKMN_DIORAMA_*` switches still turn features off.
* `gl/gl_pc_overlay.c`: the PC settings menu (F10) and the frame rate, drawn as the
  UI art direction asks (the engine's bitmap fonts, the Route Caps face, Emerald
  green panels with stepped corners) on a 640x360 canvas shown once at a whole
  scale with nearest sampling (sharp at any resolution and display scaling).

## The graphics overhaul (project-owned, 2026-10-10)

* `scene/camera.c`, `include/diorama/camera.h`: the cameras. Adventure (the
  default: the 3DS project's framing, about 11 tiles top to bottom, 42 degrees
  down, gliding after the player and looking ahead), Diorama (F7: higher, about
  24 tiles) and Photo (F9: the game stands still, the camera moves freely). One
  rig (`DioramaCameraRig`) feeds every scene shader. F12 writes a picture
  (`src/platform/png_write.c`).
* `scene_snapshot.h`: the scene is 49 x 49 cells (33 x 33 before), and outdoors
  its edge fades into the sky's colour (`gl_light.c`) instead of the dark.
* `gl/gl_object_renderer.c` (`HasSilhouette`, draw mode 5): a player (local or
  remote) or follower hidden behind a building or a tree is drawn as a
  see-through blue outline (depth test reversed, a small depth margin so the
  ground under its feet does not count). Not in Photo mode.
* `scene/weather.c`, `gl/gl_compositor.c` (`BuildWeatherImage`),
  `gl/gl_light.c`: every weather has a 3D look, so none falls back to the flat
  view any more: rain, a downpour's longer and faster drops, thunder, snow, a
  sandstorm (sandy air, streaks, a sandy haze in the distance), fog, ash, and
  bubbles under water. `PKMN_DIORAMA_WEATHER=<n>` (the `WEATHER_*` number)
  previews a weather in the 3D view only; the game's own weather is unchanged.
* `scene/tree_sprites.c`: the 3DS trees' lower row next to open ground
  (metatiles `0x1F2`, `0x1F3`) gets its crown (Petalburg Woods' cut tree tops).
* Neighbouring maps with other tiles (`scene_snapshot.h` `atlasPage`,
  `DioramaNeighbourPage`; `scene/metatile_atlas.c` `DioramaAtlas_Slot`,
  `DioramaAtlas_SetPages`, `DioramaAtlas_MapPalette`): a connected map whose
  secondary tileset differs from the map's own (Route 110 seen from Slateport,
  Route 119 from Route 118) used to be drawn as the map's border tiles until
  the player walked into it. Its cells are now published with a page (up to
  three at once): its own secondary tiles (decompressed once on the game
  thread and kept) and palettes 6 to 12 (with the night palette mix the game
  gives that tileset). The atlas has 512 more slots per page (2560 in all,
  576 x 1440); a page's palettes are faded as the map's are (each channel's
  mapping read off the live palettes, so the night tint, a weather's colour
  map and a screen fade carry over); the terrain, the 3DS trees and the
  modelled buildings (a palette row per page) draw from the page. Not done: a
  neighbour's animated tiles stand still, and its light colours (windows at
  night) are faded like the rest. `PKMN_DIORAMA_NEIGHBOURS=0` shows border
  tiles as before.
* Distant scenery (`scene_snapshot.h` `DioramaFarBlock`, `scene_snapshot.c`
  `CopyFar`; `terrain/far_field.c`, `include/diorama/far_field.h`;
  `rules.c` `DioramaRules_StaticHeights`): past the 49 x 49 cells, the world
  out to 48 tiles in blocks of 2 x 2 cells on even map coordinates - the map,
  its connected maps and the maps connected to those, the border block
  beyond - each block a column as high as the rules make one of its cells (an
  obstacle's if it has one; a tree at least a canopy's height, a little
  uneven), coloured with the average of that metatile's art (worked out once
  per tileset on the game thread) and faded along the palette line like the
  voxel trees. Outdoors only (towns, cities, routes); the edge fog then closes
  in from 30 to 47 tiles instead of 18.5-23. One vertex buffer, rebuilt when the
  camera's cell changes. Off with the effects quality off or
  `PKMN_DIORAMA_FAR=0`.
* World detail objects (phase 2; `include/diorama/props.h`, `scene/props.c`,
  `gl/gl_props.c`, `include/diorama/gl_props.h`; tools in `tools/diorama_objects`):
  the small things the map art paints flat stand up in 3D from their own
  metatile art. A catalogue per tileset (`props.c`; made by looking at the
  tilesets and at where the maps use them: `metatile_sheet.py`, `map_sheet.py`,
  `prop_candidates.py`) names a metatile's object and how it stands:
  * a slab: the art upright, a few pixels thick (signs, Rustboro's street
    lamps - the head a cell north of the post stands on the post's cell -,
    stone and iron fences; a fence running north-south turns the east-west
    fence's art to face east along its line);
  * cards: the art's halves as two thin upright cards swaying in the wind
    (tall grass, long grass, flowers);
  * a mound: the art seen from above, each opaque pixel a column, the taller
    the further it is from the art's edge (rocks in caves and lava fields,
    flower baskets, round bushes);
  * a relief: the same column mesh standing up, its back on the cell's north
    edge and bulging south (a rock or boulder drawn from the front: on sand,
    in the sea, Fallarbor's ash boulders);
  * a block: the art on top of a block (Slateport's market goods; Petalburg's
    hedges when the modelled hedges are missing).
  A prop's art is its metatile's layer with a key colour (the grass between
  blades) and its painted shadow left out (the sun's shadow map draws a real
  one); the cell's ground is drawn without the flat picture: its lower layer,
  or for art that fills its cell (the metal sign) the ground most of its
  neighbours have. The renderer keeps each prop's art in a small texture of
  its own (recomposed when the palettes fade or the tiles animate), draws
  every slab with one instanced mesh whose sides are cut per pixel by the
  art's transparency (so any art becomes little blocks with no geometry of its
  own), the cards with another, and each mound with a mesh made from its art.
  Lit like the terrain (grade, weather light, dapples, shadow map, haze); slabs
  and mounds cast shadows. The swaying ones sway harder in rain, a storm or a
  sandstorm and less in fog (`DioramaProps_Wind`), and the grass someone
  stands in or steps out of shivers. Cells the rules already stand up as pixel
  objects (Littleroot's signs) and cells a modelled building stands on
  (`DioramaProps_SetCellTaken`: the generator's own hedges, railings and sea
  rocks) are left to them. Collision and everything the game does with the
  cells are unchanged. The player's World details setting (F10, the quality
  presets: off in Low) or `PKMN_DIORAMA_PROPS=0` draws them flat.
* Every building in 3D (phase 3; `tools/diorama_buildings/auto_buildings.py`):
  the buildings no hand-made spec models (about seventy: Slateport, Mauville,
  Lilycove, Fortree, Mossdeep, Pacifidlog, Dewford, Fallarbor, Verdanturf,
  the route houses, the Battle Frontier) are found from their doors and
  modelled by the same generator as a relief read off their own drawing (a
  front as tall as the facade, the roof laid back over their cells), verified
  pixel for pixel in the GBA projection and drawn by the building renderer
  like the others (doors open on them). Sootopolis's houses, cut into the
  crater, are found by their roofs (`crater_specs`) and modelled the same way.
* Lit windows (phase 4; `tools/diorama_buildings/window_glow.py`,
  `DioramaBuildings_PageWindows`): the windows of the modelled buildings of
  outdoor maps, found on their texture pages (glass-coloured panes in a darker
  frame on upright faces), are a second channel of the page texture and glow
  warm in the evening and at night (`DioramaGLLamps_Night`), each window its
  own brightness.
* Lamp light (phase 4; `gl/gl_lamps.c`, `include/diorama/gl_lamps.h`): in the
  evening and at night (the game's clock) the street lamps among the detail
  objects light the ground, walls, trees, props and characters round them with
  a warm pool, and their glass glows. A few point lights (the 16 nearest the
  camera of the scene's lamps) with a smooth falloff and no shadows of their
  own, added in the terrain, building, tree, prop and sprite shaders
  (`DIORAMA_GLSL_LAMPS`); only in the field pass (a 3D battle's frame has
  none) and only on maps the game tints for the time of day
  (`DioramaGLLamps_NightFor`); off with the player's Lighting setting.
  `PKMN_DIORAMA_LOG=1` logs how many are lit.
* Levels and steps (performance directive phase 4, 2026-10-12): where the
  terrace solver (`tools/diorama_rules/terrace_topology.py`) leaves cells a
  level below their neighbours, the higher cell's side reaches down to the
  lower one's top (`terrain_mesh.c` `AppendSkirtFaces`, drawn with the higher
  cell's ground) - the sky showed through those steps before (the sea solved a
  level below its rocks and sandbars, a row of trees above the walk beside
  it). The rules put the sea's shallows on the sea's level (`join_water`), the
  sea's grey rocks and brown stacks on the water they stand in (lifted with the
  terraces, or at the sea's own height: `compile_rules.py` `_sea_rock_cells`,
  the generator's own finder) and ledges on a lower level on that level; such a
  ledge is a block whose sides are its whole drawing.
* Long grass: a walker the game splits into strips there (subsprite tables 4
  and 5, the lower strip behind the grass) is drawn whole; the 3D grass stands
  in front of it (`scene_snapshot.c` `HasUnsupportedSubsprites`). Routes 119
  and 120 fell back to the 2D view while the player stood in it.
* Real roofs, recessed windows, framed doors (the owner's option B,
  2026-10-12; `tools/diorama_buildings/dimensional.py`, project-owned): the
  buildings read off their drawing get a roof of their own instead of the
  drawing laid flat - pitched hipped roofs with a ridge tiled from a swatch of
  their own drawn roof, parapets round flat roofs, domes, cones and pyramids
  (huts, towers, the Battle Pyramid), the Battle Tents as domes with their
  entrance standing out - and on every building outdoors the windows and
  doors are set back with reveals, the windows with a sill. The facades are
  still proved against their drawing; the roofs and details are left out of
  the proof, so the normal camera sees a roof a little unlike its art.
* Layouts a map's scripts switch to (`setmaplayoutindex`: Route 111 without
  its Mirage Tower, Shoal Cave at high tide, Sootopolis during the legends'
  battle): the snapshot names the layout the game draws (`scene_snapshot.c`
  `CurrentLayoutId`; `gMapHeader.mapLayoutId` keeps the map's own), the
  buildings read off the map stand in those layouts too where their cells
  are the same (`gen_voxel_buildings.py` `layout_variants`), and a cave's
  switched layouts get its levels.
* Cave levels (V38): in caves the walkable floors stand at their GBA
  elevation's level - the lowest where it was, the highest a tile over it -
  and bridges (elevation 15) are decks at the floor they meet
  (`terrace_topology.py` `cave_levels`).
* Terrain cost (V37): each chunk is drawn from indices, a quad's four corners
  shaded once (`DioramaTerrain_FoldQuads`; the same triangles in the same
  order). The columns' undersides on a cell's floor are kept (V47): the
  terrain is drawn from both sides, and they are what shows through the
  pixel-wide cracks where a merged face's edge meets smaller faces (without
  them the sky showed there). `PKMN_DIORAMA_CHUNK_LOG=1` logs each chunk's
  cost.

## Mountains, caves and houses (the owner's report, 2026-10-23)

* Mountains (`compile_rules.py` `_with_mountain_art`, `mountain_art.py`
  `detect_layout`, `gl_terrain_renderer.c` `BuildMountainArtMask`): the game
  tags its mountain ground MB_MOUNTAIN_TOP for the battle background alone, the
  paths the player walks on as well as the rock. The rules stood every such
  cell up as a one-cell cliff and the renderer eroded each cell's drawing into
  a little stepped pyramid of its own, so Route 112's paths, trees and cliffs
  were rows of spikes. A walkable MB_MOUNTAIN_TOP cell is now ground
  (`behavior:MB_MOUNTAIN_TOP:walkable`) and joins no rock to it; a blocked one
  (and the secondary tileset's rock the rules join to it) is solid rock a cell
  high with its whole drawing on its top and sides, so a mountain is one mass.
  Something drawn on the cell's upper layer keeps its outline: rock stands
  straight up from it, a green thing (a mountain tree) is a rounded crown;
  pieces of the outline under six pixels are left out (they stood as stray
  pillars). General's dense forest and terraces keep their own heightmaps.
* Caves (`tools/battle_env/cave_pins.py`, `compile_rules.py` `_cave_rock_cells`,
  `rules.c` `BorderRockRecord`): the cave rock picker read tile behaviours with
  a table one value off from MB_INTERIOR_DEEP_WATER on (a comment in the enum
  counted as a name; battle_room_auto.py's reader, which the indoor battle rooms
  still use) and took only MB_NORMAL rock, so the rock tops that carry the
  cave's own MB_CAVE (Rusturf Tunnel's 9 and 17, about 330 cells) lay flat and
  read as floor. It now reads the enum without its comments, takes rock with
  no behaviour, the cave's or a one-way edge, and leaves out the cell beyond an
  arrow warp (the way out drawn on the wall's foot, which stood as a block with
  the opening on every side): the exit is a gap in the wall. Rock it chose
  before stays chosen. Cell by cell, the compiler stands up the blocked cells of
  a cave tileset's rock masses that are drawn with a metatile the cave also
  uses for floor (Granite Cave's sand); the black past a cave's edge and plain
  fills stay flat. A blocked ledge the player can never jump stands with that
  rock too (a diagonal one, which the game never jumps, or a straight one with
  no cell to jump it from): Rusturf Tunnel draws its rock in tiers with such
  ledges between them, which lay low and read as trenches through the rock
  (35 cells there, 10 in Victory Road 1F, 2 in Granite Cave 1F); a ledge the
  player jumps stays a ledge. A cave's rock beside a raised floor stands on that floor, a
  cell over it (`lift_blocked_cells` lifts to any level other than 0). Past the
  map's edge, an underground map's border block takes the record of the same
  metatile standing as rock inside the map (a border no rock is drawn with -
  Navel Rock's sky - stays flat).
* Houses (`compile_rules.py` `STANDING_WALLS`, `voxel_building.py` `HipRoof`):
  where the terrace solver puts a walk a level below the top, blocked cells
  stayed on the top level: Route 116's rest house stood a tile up on its own
  cells, their sides its facade and its sign drawn again under it, and trees
  stood on plinths. Blocked cells outdoors now stand on the level of the ground
  nearest them, as Mossdeep's already did, except the classes that stand
  between two levels (cliffs, mounds, walls); a wall that touches both levels
  keeps the top level. A hipped roof's ridge is its cap alone, a strip of the
  drawn ridge's top where the slopes meet: the 3DS port stood it up as a box
  whose front was the drawn ridge, which read as a block lying on the roof.

## Mountains, caves, cliffs, stairs and roofs (the owner's report, 2026-10-24)

* Rock masses (`gl_terrain_renderer.c` `BuildMountainArtMask`,
  `ShapeRockMasses`): a mountain's rock stood as a column a cell high per pixel
  of its drawing, so Route 106's mountain was a field of flat-topped cubes and
  Route 112's rock a maze of walls. Every mountain-art cliff is now one mass
  with the rock round it: it stands its record's height over its ground and
  rises to it from the mass's foot - over the whole cell where the mass ends to
  the south (the face the GBA draws there, its drawing lying on the slope), in
  `ROCK_MASS_EDGE` pixels on the other sides, rounded at the corners. What a
  cell's upper layer draws on the rock is relief on the mass (a crag a few
  voxels, a mountain tree's crown more); where the upper layer draws the rock's
  edge over ground in another palette (`mountain_art.py` `outline_cells`, art
  mode "cutout"), the mass follows that outline. A cell's heights depend only
  on it and its eight neighbours, the chunk's halo.
* The rest of a mountain (`mountain_art.py` `mass_extension`): the blocked
  MB_NORMAL cells joined to a mountain's rock and drawn in its palette (Route
  106's face and edges, the cave mouth's top, a reef's rock round a
  MB_MOUNTAIN_TOP cell in the sea) are rock of the same mass; they lay flat
  beside rock a cell high. The sea's rocks and boulders (`voxel_props.py`, their
  own models) and a terrace's course are left out (`compile_rules.py`
  `_prop_cells`). A reef's walkable top that no walk reaches
  (`mountain_art.py` `enclosed_tops`; none with an event on it) is its rock
  too, and a piece of rock whose walkable neighbours are all the sea stands on
  the sea's level (`compile_rules.py` `_reefs_at_sea_level`), lowered with the
  sea at run time, all its cells together (`buildings.c` `LevelReefs`): Route
  106's reefs were stacks of cubes, then a dome on a plinth of their own edge.
* Caves: a cave tileset's rock (the pins and `_cave_rock_cells`) is a mass as a
  mountain's is, so a raised floor's edge is a slope with the face drawn on it,
  not a block a cell square (Granite Cave's stepped walls).
* Faces (`terrain_mesh.c` `FaceMaterial`): on a cell drawn as a height per
  pixel (a rock mass, a terrace's course) each column's side is its own
  pixel's colour, and its steps are shaded nearly as its tops: the whole
  drawing squeezed onto every step's side drew Route 104's cliffs as walls of
  stripes.
* Stairs (`compile_rules.py` `_terrain_record`): General's stairs between two
  terrace levels (175, 207) lay flat on the lower level with the upper level's
  edge standing over them as a wall (Mossdeep, Route 104, Ever Grande); they
  are steps from the lower level up to the upper one.
* Roofs (`auto_buildings.py` `north_row`, `voxel_building.py`
  `building_art(roof_notches=True)`): the foot of a tree behind a house was
  taken as the top of its roof, and the grass under the notches of a ridge's
  top row as the roof's own colour, so Dewford's hipped roofs had a strip of
  grass along the ridge.
