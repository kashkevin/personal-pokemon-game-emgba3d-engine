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
| `src/diorama/scene/battle_stage.c`, `include/diorama/battle_stage.h` | Port of `3ds_port/src/voxel/voxel_battle.c`'s stage search (where the battle stands near the player, its scoring unchanged), written in GBA picture terms instead of the 3DS screen's and run at once instead of a slice per frame. |
| `src/diorama/scene/battle_capture.c`, `include/diorama/battle_scene.h` | Port of `voxel_battle.c`'s ground kinds and battler shadows and of `voxel_world.c`'s scenery fade fit (`VoxelWorld_ScreenFade`); new: the per-frame capture, the battle-screen check. |
| `src/diorama/gl/gl_battle.c`, `include/diorama/gl_battle.h` | Port of the battle camera and its glide (`3ds_port/src/voxel/ctr_voxel.c`: crane, dolly zoom, landing push and quake) and of `RenderBattleWorld`'s intro effects and contact shadows (`3ds_port/src/3ds_video.c`: speed lines, vignette, cinema bars, landing flash). New: the single-screen layout (picture on the bottom edge, its background carried out to wide windows), the world kept while the camera stands still, the classic-view switch (F4) during a battle, the field's 3D fade back in. |
| `src/platform/gba_easy_draw.c` (`ENABLE_DIORAMA` parts) | New: the scenery layers drawn see-through with the GBA's own blending carried in a per-pixel cover (premultiplied alpha), and each line's edge colour. The 3DS project does the same on its GPU (`sWorldLayers`). |
| `src/battle_bg.c`, `src/battle_anim.c`, `src/battle_intro.c` (`ENABLE_DIORAMA` parts) | New hooks: whether BG3 holds the scenery or a move's background (`LoadMoveBg`), and whether the intro is sliding (the 3DS port's `CtrBattleBg_MoveBgShown`, `CtrBattleIntro_Sliding`). |
| `src/diorama/gl/gl_terrain_renderer.c`, `gl_building_renderer.c`, `gl_tree_sprites.c`, `gl_light.c` (`DioramaGLLight_GetHaze`), `src/diorama/terrain/terrain_mesh.c` | The eye's height, distance and screen offset as uniforms (`DioramaCameraRig`); with the rig inactive the overworld's values, so the field is drawn exactly as before. |
| `gl_battle.c` (`DioramaGLBattle_ComposeField`), `battle_capture.c` (map sprites), `src/platform/gba_easy_draw.c` (line shifts), `src/sprite.c` and `src/battle_transition.c` (`ENABLE_DIORAMA` parts) | Port of the 3DS project's battle transition over its voxel field (`3ds_port/src/3ds_video.c` `RenderTransition`): the field's map layers see-through and its map sprites (placed on the map, or the weather's) left out of the GBA picture, the 3D field copied out and laid back in bands of lines moved as the transition moves the field's layers, the transition's own picture over it. New: the hooks that mark map sprites as the OAM buffer is built and say whether a transition plays. `PKMN_DIORAMA_TRANSITION_2D=1` keeps the cut to the flat picture. |
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
