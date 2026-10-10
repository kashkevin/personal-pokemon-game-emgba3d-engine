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
| `src/diorama/gl/gl_terrain_renderer.c`, `gl_building_renderer.c`, `src/diorama/terrain/terrain_mesh.c` | The eye's height, distance and screen offset as uniforms (`DioramaCameraRig`); with the rig inactive the overworld's values, so the field is drawn exactly as before. |

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
  visible-spawn updates), `src/fieldmap.c` (map changed).
